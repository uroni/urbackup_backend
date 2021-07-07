## Building Windows Docker Images and Uploading them to AWS Elastic Container Service
### Prereqs
* Windows with Containers (Any Windows 10 install with the Anniversary update will do)
* Docker
* AWS CLI

### Building the Image
In powershell, in the directory containing a Dockerfile.

    docker build -t <nameOfImage>:latest -m 2GB .

this will take a while, so go grab a beer or 10... you deserve it..

After the build finishes, you can run and test the image by running:

    docker run -it <nameOfImage>:latest

When you are satisfied the image is to your liking, simply exit the container.

### Publishing the Image
First we need to tag the image. Your ecr uri can be found on the AWS Console for your ecr repostiory

    docker tag <nameOfImage>:latest <your aws account number>.dkr.ecr.us-east-1.amazonaws.com/<repo name>/<nameOfImage>:latest

On windows, the next part is painful due to the way the credential store works. You need credentials for docker push to work. To do this
you use the `aws ecr get-login` command. However, this doesn't work so.... you'll need to tweek it a bit.

    aws ecr get-login > dumb_behavior.txt

Now open the `dumb_behavior.txt` file and change the `https` to `http`. Now copy the entire contents of the file and run it as a command in powershell.

Now finally, you should be able to publish the image:

    docker push <yourAccountId>.dkr.ecr.us-east-1.amazonaws.com/<repo name>:<nameOfImage>:latest

Assuming all of that works, you should now be able to use the image for things like codebuild.
